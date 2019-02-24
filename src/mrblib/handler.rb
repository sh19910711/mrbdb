class Handler
  attr_reader :last_index
  attr_reader :table
  attr_reader :iterator
  attr_reader :records

  def initialize(table)
    @table = table
    @records = []
    @last_index = 0
  end

  def rnd_init
    @iterator = records.each.with_index

    return 0
  end

  def rnd_end
    return 0
  end

  def rnd_next
    begin
      record, current_index = iterator.next
      @last_index = current_index
      columns = record.split("\0")
      table.each_field do |field|
        field.store(columns.shift)
      end
      return 0
    rescue StopIteration
      HA_ERR_END_OF_FILE
    end
  end

  def write_row
    columns = []
    table.each_field do |field|
      columns.push(field.str)
    end
    records.push(columns.join("\0"))
    return 0
  end

  def update_row
    columns = []
    table.each_field do |field|
      columns.push(field.str)
    end
    records[last_index] = columns.join("\0")

    return 0
  end

  def delete_row
    iterator.rewind
    records.delete_at(last_index)

    return 0
  end
end
